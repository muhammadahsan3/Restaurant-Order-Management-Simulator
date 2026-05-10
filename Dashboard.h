#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <string>
#include <vector>
#include <deque>
#include <pthread.h>
#include <unistd.h>
#include <ctime>

using namespace std;

// ANSI color codes
#define D_RST "\033[0m"
#define D_BOLD "\033[1m"
#define D_DIM "\033[2m"
#define D_RED "\033[31m"
#define D_GRN "\033[32m"
#define D_YLW "\033[33m"
#define D_BLU "\033[34m"
#define D_MAG "\033[35m"
#define D_CYN "\033[36m"
#define D_WHT "\033[37m"
#define D_BRED "\033[91m"
#define D_BGRN "\033[92m"
#define D_BYLW "\033[93m"
#define D_BBLU "\033[94m"
#define D_BCYN "\033[96m"
#define D_BWHT "\033[97m"

// Thread roles
enum ThreadRole
{
    ROLE_WAITER,
    ROLE_CHEF
};
enum ThreadState
{
    STATE_IDLE,
    STATE_WORKING,
    STATE_BLOCKED,
    STATE_DONE
};

//  One row in the Thread Monitor panel
struct ThreadInfo
{
    int id;
    ThreadRole role;
    ThreadState state;
    string task;   // what this thread is doing right now
    int completed; // orders placed by waiter or cooked by chef
    pid_t pid;
    pthread_t tid;

    ThreadInfo(int i, ThreadRole r, pid_t p, pthread_t t)
        : id(i), role(r), state(STATE_IDLE),
          task("Starting..."), completed(0), pid(p), tid(t) {}
};

// One entry in the Completed Orders panel 
struct DoneOrder
{
    int order_id;
    string item_name;
    int table;
    int chef_id;
    int cook_time;
    double wait_time;
};

// All shared state the dashboard reads 
struct DashboardState
{
    pthread_mutex_t mutex;

    vector<ThreadInfo> threads;   // one entry per waiter/chef thread
    deque<DoneOrder> done_orders; // newest at back,shown newest first
    static const int MAX_DONE = 12;

    int queue_size;
    int queue_capacity;
    int total_submitted;
    int total_cooked;
    int total_cook_time;

    bool simulation_done;
    time_t start_time;

    DashboardState()
        : queue_size(0), queue_capacity(0),
          total_submitted(0), total_cooked(0), total_cook_time(0),
          simulation_done(false)
    {
        pthread_mutex_init(&mutex, nullptr);
        start_time = time(nullptr);
    }
    ~DashboardState() { pthread_mutex_destroy(&mutex); }
};

// Helper functions — called from Waiter/Chef threads in project.cpp 
//    All are thread-safe (lock and unlock mutex internally)

void dashRegisterThread(DashboardState *d, int id, ThreadRole role,
                        pid_t pid, pthread_t tid);

void dashSetState(DashboardState *d, int id, ThreadRole role,
                  ThreadState state, const string &task);

void dashOrderSubmitted(DashboardState *d, int new_queue_size);

void dashOrderCooked(DashboardState *d, const DoneOrder &order,
                     int new_queue_size);

void dashThreadDone(DashboardState *d, int id, ThreadRole role,
                    int completed);

// Dashboard class
class Dashboard
{
private:
    DashboardState *state;
    bool running;
    pthread_t render_thread; // one dedicated render thread

    // Panel drawers
    void drawHeader();
    void drawQueuePanel(int row, int col);
    void drawStatsPanel(int row, int col);
    void drawThreadPanel(int &row);
    void drawOrdersPanel(int &row);
    void drawFooter(int row);

    void render(); // one full repaint (called every 600ms)

    // Static helpers
    static string elapsed(time_t start);
    static string stateStr(ThreadState s);
    static string stateColor(ThreadState s);
    static string fillBar(int cur, int mx, int w);

    static void mv(int r, int c);
    static void box(int row, int col, int w, int h,
                    const string &title, const string &color);

    // pthread entry point
    static void *renderLoop(void *arg);

public:
    explicit Dashboard(DashboardState *s)
        : state(s), running(false) {}
    ~Dashboard() { stop(); }

    void start(); //  render thread call before waiters/chefs
    void stop();  // call after  waiters/chefs
};

#endif
