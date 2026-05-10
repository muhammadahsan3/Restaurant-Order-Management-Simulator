#include "Dashboard.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <cstdio>

using namespace std;
//  Internal helper , find a thread entry (caller must hold mutex)
static ThreadInfo *findThread(DashboardState *d, int id, ThreadRole role)
{
    for (auto &t : d->threads)
        if (t.id == id && t.role == role)
            return &t;
    return nullptr;
}

//  PUBLIC HELPER FUNCTIONS  (called from project.cpp)
void dashRegisterThread(DashboardState *d, int id, ThreadRole role,
                        pid_t pid, pthread_t tid)
{
    pthread_mutex_lock(&d->mutex);
    d->threads.emplace_back(id, role, pid, tid);
    pthread_mutex_unlock(&d->mutex);
}

void dashSetState(DashboardState *d, int id, ThreadRole role,
                  ThreadState state, const string &task)
{
    pthread_mutex_lock(&d->mutex);
    ThreadInfo *t = findThread(d, id, role);
    if (t)
    {
        t->state = state;
        t->task = task;
    }
    pthread_mutex_unlock(&d->mutex);
}

void dashOrderSubmitted(DashboardState *d, int new_queue_size)
{
    pthread_mutex_lock(&d->mutex);
    d->total_submitted++;
    d->queue_size = new_queue_size;
    pthread_mutex_unlock(&d->mutex);
}

void dashOrderCooked(DashboardState *d, const DoneOrder &order,
                     int new_queue_size)
{
    pthread_mutex_lock(&d->mutex);
    d->total_cooked++;
    d->total_cook_time += order.cook_time;
    d->queue_size = new_queue_size;
    d->done_orders.push_back(order);
    if ((int)d->done_orders.size() > DashboardState::MAX_DONE)
        d->done_orders.pop_front();
    pthread_mutex_unlock(&d->mutex);
}

void dashThreadDone(DashboardState *d, int id, ThreadRole role, int completed)
{
    pthread_mutex_lock(&d->mutex);
    ThreadInfo *t = findThread(d, id, role);
    if (t)
    {
        t->state = STATE_DONE;
        t->task = "Finished";
        t->completed = completed;
    }
    pthread_mutex_unlock(&d->mutex);
}

//  Dashboard (start() and stop() )
void Dashboard::start()
{
    running = true;
    cout << "\033[?25l" << "\033[2J" << "\033[H" << flush;
    pthread_create(&render_thread, nullptr, Dashboard::renderLoop, this);
}

void Dashboard::stop()
{
    if (!running)
        return;
    running = false;
    pthread_join(render_thread, nullptr); 
    render();                         // final frame
    cout << "\033[?25h" << D_RST << flush;
}

void *Dashboard::renderLoop(void *arg)
{
    Dashboard *d = static_cast<Dashboard *>(arg);
    while (d->running)
    {
        d->render();
        usleep(600000); //usleep() — 600 000 µs = 600 ms
    }
    return nullptr;
}

//  STATIC DRAWING HELPERS
void Dashboard::mv(int r, int c) { 
    printf("\033[%d;%dH", r, c); 
}

string Dashboard::elapsed(time_t start)
{
    int s = (int)difftime(time(nullptr), start);
    int m = s / 60;
    s %= 60;
    ostringstream o;
    o << setw(2) << setfill('0') << m << ":"
      << setw(2) << setfill('0') << s;
    return o.str();
}

string Dashboard::stateStr(ThreadState s)
{
    switch (s)
    {
    case STATE_IDLE:
        return " IDLE  ";
    case STATE_WORKING:
        return "WORKING";
    case STATE_BLOCKED:
        return "BLOCKED";
    case STATE_DONE:
        return " DONE  ";
    default:
        return "       ";
    }
}

string Dashboard::stateColor(ThreadState s)
{
    switch (s)
    {
    case STATE_IDLE:
        return string(D_DIM) + D_WHT;
    case STATE_WORKING:
        return D_BGRN;
    case STATE_BLOCKED:
        return D_BYLW;
    case STATE_DONE:
        return D_BCYN;
    default:
        return D_RST;
    }
}

// filling bar: (green then yellow then red) as queue fills up
string Dashboard::fillBar(int cur, int mx, int w)
{
    if (mx <= 0)
        return string(w, '-');
    int filled = min((cur * w) / mx, w);
    double pct = (double)cur / mx;

    string col;
    if (pct >= 0.80)
        col = D_BRED;
    else if (pct >= 0.50)
        col = D_BYLW;
    else
        col = D_BGRN;

    string b = col;
    for (int i = 0; i < filled; i++)
        b += "\xe2\x96\x88"; 
    b += string(D_DIM) + D_WHT;
    for (int i = filled; i < w; i++)
        b += "\xe2\x96\x91"; 
    b += D_RST;
    return b;
}

void Dashboard::box(int row, int col, int w, int h,
                    const string &title, const string &color)
{
    // top border
    mv(row, col);
    printf("%s%s\xe2\x95\x94", D_BOLD, color.c_str()); 
    if (!title.empty())
    {
        string t = " " + title + " ";
        int pad = w - 2 - (int)t.size();
        int lp = pad / 2, rp = pad - lp;
        for (int i = 0; i < lp; i++)
            printf("\xe2\x95\x90");
        printf("%s%s%s%s", D_BWHT, t.c_str(), color.c_str(), D_BOLD);
        for (int i = 0; i < rp; i++)
            printf("\xe2\x95\x90");
    }
    else
    {
        for (int i = 0; i < w - 2; i++)
            printf("\xe2\x95\x90");
    }
    printf("\xe2\x95\x97%s", D_RST); 

    // sides
    for (int r = 1; r < h - 1; r++)
    {
        mv(row + r, col);
        printf("%s%s\xe2\x95\x91%s%*s%s%s\xe2\x95\x91%s",
               D_BOLD, color.c_str(), D_RST,
               w - 2, "",
               D_BOLD, color.c_str(), D_RST);
    }

    // bottom border
    mv(row + h - 1, col);
    printf("%s%s\xe2\x95\x9a", D_BOLD, color.c_str()); 
    for (int i = 0; i < w - 2; i++)
        printf("\xe2\x95\x90");
    printf("\xe2\x95\x9d%s", D_RST); 
}

//  PANEL DRAWERS
void Dashboard::drawHeader()
{
    pthread_mutex_lock(&state->mutex);
    string el = elapsed(state->start_time);
    bool done = state->simulation_done;
    pthread_mutex_unlock(&state->mutex);

    mv(1, 1);
    printf("%s%s", D_BOLD, "\033[44m");
    printf("  RESTAURANT ORDER MANAGEMENT SIMULATOR"
           "  |  OS Project  |  FAST NUCES Karachi      ");
    printf("%s\n", D_RST);

    mv(2, 1);
    printf("%s  Waiters=Producers | Chefs=Consumers | Mutex+Semaphores | "
           "fork+pipe | Elapsed: %s%s%s%s\n",
           D_DIM D_WHT,
           D_BWHT, el.c_str(),
           done ? (string(D_BGRN) + "  [SIMULATION COMPLETE]").c_str() : "",
           D_RST);

    mv(3, 1);
    printf("%s%s%s\n", D_BLU, string(78, '-').c_str(), D_RST);
}

// Queue panel, left half, rows 4-14
void Dashboard::drawQueuePanel(int row, int col)
{
    pthread_mutex_lock(&state->mutex);
    int qs = state->queue_size;
    int qc = state->queue_capacity;
    int sub = state->total_submitted;
    int cooked = state->total_cooked;
    int ct = state->total_cook_time;
    bool done = state->simulation_done;
    pthread_mutex_unlock(&state->mutex);

    box(row, col, 38, 12, "ORDER QUEUE", D_CYN);

    int pct = qc > 0 ? qs * 100 / qc : 0;

    mv(row + 1, col + 2);
    printf("%sFill   %s%s %s%3d%%%s",
           D_WHT, D_RST, fillBar(qs, qc > 0 ? qc : 1, 18).c_str(), D_BYLW, pct, D_RST);
    mv(row + 2, col + 2);
    printf("%sQueue  %s%s%d / %d orders%s",
           D_WHT, D_RST, D_BWHT, qs, qc, D_RST);
    mv(row + 3, col + 2);
    printf("%sAdded  %s%s%d%s", D_WHT, D_RST, D_BGRN, sub, D_RST);
    mv(row + 4, col + 2);
    printf("%sCooked %s%s%d%s", D_WHT, D_RST, D_BGRN, cooked, D_RST);
    mv(row + 5, col + 2);
    printf("%sPending%s%s%d%s", D_WHT, D_RST,
           (sub - cooked) > 0 ? D_BYLW : D_BGRN, sub - cooked, D_RST);
    mv(row + 6, col + 2);
    printf("%sAvg cook%s%s%ds%s", D_WHT, D_RST, D_BWHT,
           cooked > 0 ? ct / cooked : 0, D_RST);
    mv(row + 8, col + 2);
    if (done)
        printf("%s%s  SIMULATION COMPLETE  %s", D_BOLD, D_BGRN, D_RST);
    else if (sub > cooked)
        printf("%s  Processing orders...%s", D_BYLW, D_RST);
    else
        printf("%s  Waiting for orders...%s", D_DIM D_WHT, D_RST);
}

// Stats panel,right half, rows 4-14
void Dashboard::drawStatsPanel(int row, int col)
{
    pthread_mutex_lock(&state->mutex);
    int sub = state->total_submitted;
    int cooked = state->total_cooked;
    int nw = 0, nc = 0, aw = 0, ac = 0;
    for (auto &t : state->threads)
    {
        bool active = (t.state == STATE_WORKING || t.state == STATE_BLOCKED);
        if (t.role == ROLE_WAITER)
        {
            nw++;
            if (active)
                aw++;
        }
        else
        {
            nc++;
            if (active)
                ac++;
        }
    }
    pthread_mutex_unlock(&state->mutex);

    box(row, col, 39, 12, "SESSION STATS", D_MAG);

    auto ln = [&](int r, const char *lbl, const string &val, const char *c)
    {
        mv(row + r, col + 2);
        printf("%s%-20s%s%s%s", D_WHT, lbl, D_RST, c, val.c_str());
        printf("%s", D_RST);
    };

    ln(1, "Waiters (threads)", to_string(nw) + " total  " + to_string(aw) + " active", D_BBLU);
    ln(2, "Chefs   (threads)", to_string(nc) + " total  " + to_string(ac) + " active", D_BYLW);
    ln(3, "Orders submitted", to_string(sub), D_BGRN);
    ln(4, "Orders cooked", to_string(cooked), D_BGRN);

    int pct = sub > 0 ? cooked * 100 / sub : 0;
    ln(5, "Completion", to_string(pct) + "%", pct == 100 ? D_BGRN : D_BYLW);

    mv(row + 6, col + 2);
    printf("%sProgress  %s%s", D_WHT, D_RST,
           fillBar(cooked, sub > 0 ? sub : 1, 22).c_str());

    mv(row + 8, col + 2);
    printf("%sOS: pthread  mutex  sem  fork  pipe%s", D_DIM D_CYN, D_RST);
}

// Thread monitor panel , full width, dynamic height
void Dashboard::drawThreadPanel(int &row)
{
    pthread_mutex_lock(&state->mutex);
    vector<ThreadInfo> threads = state->threads;
    pthread_mutex_unlock(&state->mutex);

    int h = max((int)threads.size() + 4, 5);
    box(row, 1, 78, h,
        "THREAD MONITOR  [ pthread_create | pthread_self | sleep | getpid ]",
        D_BBLU);

    mv(row + 1, 3);
    printf("%s%s  Role    ID  PID        State      Task                          Done%s",
           D_BOLD, D_CYN, D_RST);
    mv(row + 2, 3);
    printf("%s%s%s", D_DIM D_BLU, string(73, '.').c_str(), D_RST);

    if (threads.empty())
    {
        mv(row + 3, 5);
        printf("%s  Threads starting...%s", D_DIM D_WHT, D_RST);
    }

    for (int i = 0; i < (int)threads.size(); i++)
    {
        const ThreadInfo &t = threads[i];
        mv(row + 3 + i, 3);

        if (t.role == ROLE_WAITER)
            printf("%s%s  WAITER%s", D_BOLD, D_BBLU, D_RST);
        else
            printf("%s%s  CHEF  %s", D_BOLD, D_BYLW, D_RST);

        printf("  %s%2d%s", D_BWHT, t.id, D_RST);
        printf("  %s%8d%s", D_DIM D_WHT, (int)t.pid, D_RST);
        printf("  %s[%s]%s",
               stateColor(t.state).c_str(), stateStr(t.state).c_str(), D_RST);

        string task = t.task;
        if ((int)task.size() > 30)
            task = task.substr(0, 27) + "...";
        printf("  %s%-30s%s", D_DIM D_WHT, task.c_str(), D_RST);
        printf("  %s%3d%s", D_BGRN, t.completed, D_RST);
    }

    row += h;
}

// Completed orders panel,full width, 8 rows
void Dashboard::drawOrdersPanel(int &row)
{
    pthread_mutex_lock(&state->mutex);
    deque<DoneOrder> done = state->done_orders;
    pthread_mutex_unlock(&state->mutex);

    int show = min((int)done.size(), 6);
    int h = show + 4;
    if (h < 5)
        h = 5;

    box(row, 1, 78, h, "COMPLETED ORDERS", D_BGRN);

    mv(row + 1, 3);
    printf("%s%s  Order    Item            Table  Chef  Cook  Wait  Status%s",
           D_BOLD, D_CYN, D_RST);
    mv(row + 2, 3);
    printf("%s%s%s", D_DIM D_GRN, string(73, '.').c_str(), D_RST);

    if (done.empty())
    {
        mv(row + 3, 5);
        printf("%s  No completed orders yet...%s", D_DIM D_WHT, D_RST);
    }
    else
    {
        int shown = 0;
        for (auto it = done.rbegin(); it != done.rend() && shown < show; ++it, ++shown)
        {
            const DoneOrder &o = *it;
            mv(row + 3 + shown, 3);

            printf("  %s#%03d%s", D_BWHT, o.order_id, D_RST);
            printf("  %s%-14s%s", D_WHT, o.item_name.substr(0, 14).c_str(), D_RST);
            printf("  %s%3d%s", D_BYLW, o.table, D_RST);
            printf("  %s%4d%s", D_BBLU, o.chef_id, D_RST);

            const char *ctc = o.cook_time <= 1 ? D_BGRN : o.cook_time <= 2 ? D_BYLW
                                                                           : D_BRED;
            printf("  %s%3ds%s", ctc, o.cook_time, D_RST);
            printf("  %s%3.0fs%s", D_CYN, o.wait_time, D_RST);
            printf("  %s%sDONE%s", D_BOLD, D_BGRN, D_RST);
        }
    }

    row += h;
}

// Footer bar
void Dashboard::drawFooter(int row)
{
    mv(row, 1);
    printf("%s%s%s\n", D_DIM D_BLU, string(78, '-').c_str(), D_RST);
    mv(row + 1, 1);
    printf("%s  OS Concepts: %s%spthread_create%s  %spthread_mutex%s  "
           "%ssem_wait/post%s  %sfork+pipe%s  %swaitpid%s  "
           "%ssleep/usleep%s  %sgetpid%s\n",
           D_DIM D_CYN,
           D_RST, D_BOLD D_WHT, D_RST,
           D_BYLW, D_RST,
           D_BGRN, D_RST,
           D_BBLU, D_RST,
           D_MAG, D_RST,
           D_CYN, D_RST,
           D_BYLW, D_RST);
}

//  render() full repaint with no flicker (home cursor, don't clear)
void Dashboard::render()
{
    printf("\033[H"); // cursor home no clear = no flicker

    drawHeader(); // rows 1-3

    int panelRow = 4;
    drawQueuePanel(panelRow, 1);  // left  col 1-38
    drawStatsPanel(panelRow, 40); // right col 40-78
    int row = panelRow + 12;      // both panels are 12 rows tall

    drawThreadPanel(row);
    drawOrdersPanel(row);
    drawFooter(row);

    fflush(stdout);
}
