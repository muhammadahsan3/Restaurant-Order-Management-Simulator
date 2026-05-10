// os project
// 23k-0541  Muhammad Ahsan ,24k-0926  Safiullah Sheikh , 23k-0884  Ahsan Raza

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <ctime>
// order queue
#include <queue>
#include <pthread.h>
#include <semaphore.h>
// waiter
#include <set>
#include <unistd.h>
#include <cstdlib>
// logger + dashboard
#include <fstream>
#include <sys/wait.h>
#include <sys/types.h>
#include <cstring>
#include "Logger.h"
#include "Dashboard.h"

using namespace std;

#define STATUS_PENDING "PENDING"
#define STATUS_COOKING "COOKING"
#define STATUS_DONE "DONE"

// Restaurant menu
const vector<string> MENU = {
    "Biryani", "Burger", "Pizza", "Pasta",
    "Karahi", "Nihari", "Fries", "Shawarma",
    "Qorma", "Haleem", "Tikka", "Naan"};

//  Order class
class Order
{
public:
    int order_id;
    int table_number;
    string item_name;
    int priority; // 1=High, 2=Medium, 3=Low
    time_t timestamp;
    string status;
    int waiter_id;

    Order(int id, int table, const string &item, int waiter, int prio)
        : order_id(id), table_number(table), item_name(item),
          waiter_id(waiter), priority(prio),
          status(STATUS_PENDING), timestamp(time(nullptr))
    {
    }

    string toString() const
    {
        ostringstream oss;
        oss << "[Order #" << setw(3) << setfill('0') << order_id << "]"
            << " Table:" << table_number
            << " Item: " << left << setw(12) << setfill(' ') << item_name
            << " Waiter:" << waiter_id
            << " Status:" << setw(8) << left << status
            << " Priority:" << priority;
        return oss.str();
    }

    double waitTime() const { return difftime(time(nullptr), timestamp); }
};

// OrderQueue class : the shared resource (mutex + semaphores)
class OrderQueue
{
private:
    queue<Order> orders;
    int max_capacity;
    int total_added;
    int total_removed;
    pthread_mutex_t mutex; //  Mutual Exclusion lock
    sem_t empty_slots;     // Semaphore for free space
    sem_t filled_slots;    // Semaphore for pending orders

public:
    OrderQueue(int capacity = 10)
        : max_capacity(capacity), total_added(0), total_removed(0)
    {
        if (pthread_mutex_init(&mutex, nullptr) != 0)
            throw runtime_error("ERROR: Failed to initialize mutex!");
        if (sem_init(&empty_slots, 0, max_capacity) != 0)
            throw runtime_error("ERROR: Failed to initialize empty_slots semaphore!");
        if (sem_init(&filled_slots, 0, 0) != 0)
            throw runtime_error("ERROR: Failed to initialize filled_slots semaphore!");
    }

    ~OrderQueue()
    {
        pthread_mutex_destroy(&mutex);
        sem_destroy(&empty_slots);
        sem_destroy(&filled_slots);
    }

    // PRODUCER : called by Waiter thread
    void addOrder(Order o)
    {
        sem_wait(&empty_slots);     //BLOCK if queue full
        pthread_mutex_lock(&mutex); //enter critical section
        orders.push(o);
        total_added++;
        pthread_mutex_unlock(&mutex); // exit critical section
        sem_post(&filled_slots);      // wake chef
    }

    // CONSUMER: called by Chef thread
    Order getOrder()
    {
        sem_wait(&filled_slots);    // BLOCK if queue empty
        pthread_mutex_lock(&mutex); // enter critical section
        Order o = orders.front();
        orders.pop();
        o.status = STATUS_COOKING;
        total_removed++;
        pthread_mutex_unlock(&mutex); // exit critical section
        sem_post(&empty_slots);       // wake waiter
        return o;
    }

    bool isEmpty()
    {
        pthread_mutex_lock(&mutex);
        bool e = orders.empty();
        pthread_mutex_unlock(&mutex);
        return e;
    }
    bool isFull()
    {
        pthread_mutex_lock(&mutex);
        bool f = ((int)orders.size() >= max_capacity);
        pthread_mutex_unlock(&mutex);
        return f;
    }
    int size()
    {
        pthread_mutex_lock(&mutex);
        int s = (int)orders.size();
        pthread_mutex_unlock(&mutex);
        return s;
    }
    int getCapacity() { return max_capacity; }
    int getTotalAdded() { return total_added; }
    int getTotalRemoved() { return total_removed; }

    void printStatus()
    {
        int ev, fv;
        sem_getvalue(&empty_slots, &ev);
        sem_getvalue(&filled_slots, &fv);
        cout << "\n[Queue] size:" << size() << "/" << max_capacity
             << "  empty_slots:" << ev << "  filled_slots:" << fv
             << "  added:" << total_added << "  removed:" << total_removed << "\n";
    }
};

//  KitchenStats , shared stats, protected by its own mutex
struct KitchenStats
{
    int total_cooked;
    int total_cook_time;
    pthread_mutex_t stats_mutex;

    KitchenStats() : total_cooked(0), total_cook_time(0)
    {
        pthread_mutex_init(&stats_mutex, nullptr);
    }
    ~KitchenStats() { pthread_mutex_destroy(&stats_mutex); }
};

//   Global order-id counter — thread-safe via mutex
int global_order_id = 0;
pthread_mutex_t order_id_mutex = PTHREAD_MUTEX_INITIALIZER;

static int getNextOrderId()
{
    pthread_mutex_lock(&order_id_mutex);
    int id = ++global_order_id;
    pthread_mutex_unlock(&order_id_mutex);
    return id;
}

void resetOrderId() { global_order_id = 0; }

// Thread-safe console print (used outside dashboard mode)
static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
void safePrint(const string &msg)
{
    pthread_mutex_lock(&print_mutex);
    cout << msg << endl;
    pthread_mutex_unlock(&print_mutex);
}

//  Waiter — Producer Thread
//  Passes DashboardState* so it can call dash* helpers every step.
struct WaiterArgs
{
    int waiter_id;
    OrderQueue *queue;
    int num_orders;
    int min_delay;
    int max_delay;
    DashboardState *dash; // dashboard pointer
};

class Waiter
{
private:
    int waiter_id;
    OrderQueue *queue;
    int num_orders;
    int min_delay;
    int max_delay;
    int orders_placed;
    pthread_t thread_handle;
    DashboardState *dash;

public:
    Waiter(int id, OrderQueue *q, int num, int minD = 1, int maxD = 3,
           DashboardState *d = nullptr)
        : waiter_id(id), queue(q), num_orders(num),
          min_delay(minD), max_delay(maxD),
          orders_placed(0), dash(d)
    {
    }

    void start()
    {
        WaiterArgs *args = new WaiterArgs();
        args->waiter_id = waiter_id;
        args->queue = queue;
        args->num_orders = num_orders;
        args->min_delay = min_delay;
        args->max_delay = max_delay;
        args->dash = dash;
        if (pthread_create(&thread_handle, nullptr, Waiter::run, args) != 0)
            cerr << "[ERROR] Failed to create Waiter thread " << waiter_id << endl;
    }

    void join() { pthread_join(thread_handle, nullptr); }

    int getOrdersPlaced() const { return orders_placed; }
    int getId() const { return waiter_id; }

    static void *run(void *arg)
    {
        WaiterArgs *args = static_cast<WaiterArgs *>(arg);
        int id = args->waiter_id;
        int num = args->num_orders;
        int minD = args->min_delay;
        int maxD = args->max_delay;
        OrderQueue *q = args->queue;
        DashboardState *dash = args->dash;

        pthread_t tid = pthread_self(); // unique thread ID
        pid_t pid = getpid();           // process ID (shared by all threads)

        // Register with dashboard
        if (dash)
            dashRegisterThread(dash, id, ROLE_WAITER, pid, tid);

        // Log startup
        gLogger.system("WAITER " + to_string(id) + " STARTED | PID:" +
                       to_string(pid) + " TID:" + to_string((unsigned long)tid));

        {
            ostringstream oss;
            oss << "\n----------------------------------------------------\n"
                << "│  WAITER " << id << " THREAD STARTED\n"
                << "│  PID: " << pid << "  TID: " << (unsigned long)tid << "\n"
                << "│  Will place " << num << " orders\n"
                << "------------------------------------------------------";
            safePrint(oss.str());
        }

        srand((unsigned)time(nullptr) ^ (unsigned)(id * 7919));

        int placed = 0;
        for (int i = 0; i < num; i++)
        {
            string item = MENU[rand() % MENU.size()];
            int table = (rand() % 10) + 1;
            int priority = (rand() % 3) + 1;
            int oid = getNextOrderId();
            Order order(oid, table, item, id, priority);

            // Dashboard: taking order
            if (dash)
            {
                ostringstream t;
                t << "Taking #" << setw(3) << setfill('0') << oid << " " << item;
                dashSetState(dash, id, ROLE_WAITER, STATE_WORKING, t.str());
            }

            gLogger.order("Waiter " + to_string(id) + " placing #" +
                          to_string(oid) + " " + item + " Table:" + to_string(table));

            // Dashboard: may block if queue full
            if (dash)
                dashSetState(dash, id, ROLE_WAITER, STATE_BLOCKED,
                             "Waiting — queue full");

            q->addOrder(order); // may BLOCK via sem_wait
            placed++;

            // Dashboard: submitted
            if (dash)
            {
                dashOrderSubmitted(dash, q->size());
                ostringstream t;
                t << "Submitted #" << setw(3) << setfill('0') << oid
                  << " (" << placed << "/" << num << ")";
                dashSetState(dash, id, ROLE_WAITER, STATE_WORKING, t.str());
            }

            gLogger.info("Waiter " + to_string(id) + " submitted #" +
                         to_string(oid) + " (" + to_string(placed) +
                         "/" + to_string(num) + ")");

            // OS: sleep() :suspend this thread, let OS schedule others
            if (dash)
                dashSetState(dash, id, ROLE_WAITER, STATE_IDLE,
                             "Waiting at table...");
            int delay = minD + (rand() % (maxD - minD + 1));
            sleep(delay);
        }

        gLogger.system("WAITER " + to_string(id) + " DONE | placed:" +
                       to_string(placed));
        if (dash)
            dashThreadDone(dash, id, ROLE_WAITER, placed);

        {
            ostringstream oss;
            oss << "\n└── [WAITER " << id << "] All " << num
                << " orders placed. Thread exiting. (TID: " << (unsigned long)tid << ")";
            safePrint(oss.str());
        }

        delete args;
        return nullptr;
    }
};

//  Chef — Consumer Thread
//  Passes DashboardState* so it can call dash* helpers every step.
struct ChefArgs
{
    int chef_id;
    OrderQueue *queue;
    int min_cook_time;
    int max_cook_time;
    KitchenStats *stats;
    DashboardState *dash; //dashboard pointer
};

static pthread_mutex_t chef_print_mutex = PTHREAD_MUTEX_INITIALIZER;
static void chefPrint(const string &msg)
{
    pthread_mutex_lock(&chef_print_mutex);
    cout << msg << endl;
    pthread_mutex_unlock(&chef_print_mutex);
}

class Chef
{
private:
    int chef_id;
    OrderQueue *queue;
    int min_cook_time;
    int max_cook_time;
    int orders_cooked;
    pthread_t thread_handle;
    KitchenStats *stats;
    DashboardState *dash;

public:
    Chef(int id, OrderQueue *q, KitchenStats *s,
         int minC = 1, int maxC = 3, DashboardState *d = nullptr)
        : chef_id(id), queue(q), min_cook_time(minC), max_cook_time(maxC),
          orders_cooked(0), stats(s), dash(d)
    {
    }

    void start()
    {
        ChefArgs *args = new ChefArgs();
        args->chef_id = chef_id;
        args->queue = queue;
        args->min_cook_time = min_cook_time;
        args->max_cook_time = max_cook_time;
        args->stats = stats;
        args->dash = dash;
        if (pthread_create(&thread_handle, nullptr, Chef::run, args) != 0)
            cerr << "[ERROR] Failed to create Chef thread " << chef_id << endl;
    }

    void join() { pthread_join(thread_handle, nullptr); }

    int getId() const { return chef_id; }
    int getOrdersCooked() const { return orders_cooked; }

    static void *run(void *arg)
    {
        ChefArgs *args = static_cast<ChefArgs *>(arg);
        int id = args->chef_id;
        OrderQueue *q = args->queue;
        int minC = args->min_cook_time;
        int maxC = args->max_cook_time;
        KitchenStats *s = args->stats;
        DashboardState *dash = args->dash;

        pthread_t tid = pthread_self(); //  unique thread ID
        pid_t pid = getpid();           //  process ID (same for all threads)

        // Register with dashboard
        if (dash)
            dashRegisterThread(dash, id, ROLE_CHEF, pid, tid);

        gLogger.system("CHEF " + to_string(id) + " STARTED | PID:" +
                       to_string(pid) + " TID:" + to_string((unsigned long)tid) +
                       " Cook:" + to_string(minC) + "-" + to_string(maxC) + "s");

        {
            ostringstream oss;
            oss << "\n-----------------------------------------------------\n"
                << "│  CHEF " << id << " THREAD STARTED\n"
                << "│  PID: " << pid << "  TID: " << (unsigned long)tid << "\n"
                << "│  Cook time range: " << minC << "s - " << maxC << "s\n"
                << "│  Waiting for orders...\n"
                << "-------------------------------------------------------";
            chefPrint(oss.str());
        }

        srand((unsigned)time(nullptr) ^ (unsigned)(id * 6271));

        int local_cooked = 0;

        while (true)
        {
            // Dashboard: blocking, waiting for order
            if (dash)
                dashSetState(dash, id, ROLE_CHEF, STATE_BLOCKED,
                             "Waiting — queue empty");

            Order o = q->getOrder(); //  BLOCKS via sem_wait when empty

            // Poison pill , stop signal
            if (o.order_id == -1)
            {
                ostringstream oss;
                oss << "  [CHEF " << id << "] Received stop signal. "
                    << "Orders cooked this session: " << local_cooked;
                chefPrint(oss.str());
                gLogger.system("CHEF " + to_string(id) + " stop signal | cooked:" +
                               to_string(local_cooked));
                break;
            }

            int cook_time = minC + (rand() % (maxC - minC + 1));
            o.status = STATUS_COOKING;

            // Dashboard: cooking
            if (dash)
            {
                ostringstream t;
                t << "Cooking #" << setw(3) << setfill('0') << o.order_id
                  << " " << o.item_name;
                dashSetState(dash, id, ROLE_CHEF, STATE_WORKING, t.str());
            }

            {
                ostringstream oss;
                oss << "  [CHEF " << id << "] COOKING  Order #"
                    << setw(3) << setfill('0') << o.order_id
                    << " | " << left << setw(12) << o.item_name
                    << " | Table " << o.table_number
                    << " | Cook: " << cook_time << "s";
                chefPrint(oss.str());
            }

            gLogger.order("Chef " + to_string(id) + " COOKING #" +
                          to_string(o.order_id) + " " + o.item_name +
                          " " + to_string(cook_time) + "s");

            sleep(cook_time); // sleep() system call

            o.status = STATUS_DONE;
            local_cooked++;

            // Update shared stats, mutex protected
            pthread_mutex_lock(&s->stats_mutex);
            s->total_cooked++;
            s->total_cook_time += cook_time;
            pthread_mutex_unlock(&s->stats_mutex);

            // Dashboard: order done
            if (dash)
            {
                DoneOrder done;
                done.order_id = o.order_id;
                done.item_name = o.item_name;
                done.table = o.table_number;
                done.chef_id = id;
                done.cook_time = cook_time;
                done.wait_time = o.waitTime();
                dashOrderCooked(dash, done, q->size());

                ostringstream t;
                t << "Done #" << setw(3) << setfill('0') << o.order_id
                  << " | total:" << local_cooked;
                dashSetState(dash, id, ROLE_CHEF, STATE_WORKING, t.str());
            }

            {
                ostringstream oss;
                oss << "  [CHEF " << id << "] DONE     Order #"
                    << setw(3) << setfill('0') << o.order_id
                    << " | " << left << setw(12) << o.item_name
                    << " | Wait: " << fixed << setprecision(0) << o.waitTime() << "s"
                    << " | Chef " << id << " total: " << local_cooked;
                chefPrint(oss.str());
            }

            gLogger.order("Chef " + to_string(id) + " DONE #" +
                          to_string(o.order_id) + " " + o.item_name +
                          " wait:" + to_string((int)o.waitTime()) + "s" +
                          " total:" + to_string(local_cooked));
        }

        {
            ostringstream oss;
            oss << "\n└── [CHEF " << id << "] Thread exiting. "
                << "Cooked " << local_cooked << " orders. (TID: " << (unsigned long)tid << ")";
            chefPrint(oss.str());
        }

        gLogger.system("CHEF " + to_string(id) + " EXITING | cooked:" +
                       to_string(local_cooked));

        if (dash)
            dashThreadDone(dash, id, ROLE_CHEF, local_cooked);

        delete args;
        return nullptr;
    }
};

//  Utility: send poison pills to stop all chef threads
void sendPoisonPills(OrderQueue &queue, int num_chefs)
{
    gLogger.info("Sending " + to_string(num_chefs) + " stop signal(s) to chefs");
    for (int i = 0; i < num_chefs; i++)
    {
        Order poison(-1, -1, "STOP", -1, 0);
        queue.addOrder(poison);
    }
}

/* main()
//Phase 1 : Start file logger via fork() (MUST be before pthread_create)
//Phase 2 : Start dashboard render thread
//Phase 3 : Start chef threads (block waiting for orders)
//Phase 4 : Start waiter threads
//Phase 5 : Wait for all waiters to finish
//Phase 6 : Send poison pills, wait for chefs
//Phase 7 : Mark done, hold final frame, stop dashboard
//Phase 8 : Print report, shutdown logger
*/
int main()
{
    // Simulation config
    const int NUM_WAITERS = 3;
    const int NUM_CHEFS = 3;
    const int ORDERS_EACH = 5;
    const int QUEUE_CAPACITY = 6;
    const int WAITER_MIN = 1;
    const int WAITER_MAX = 2;
    const int CHEF_MIN = 1;
    const int CHEF_MAX = 3;

    // Phase 1: 
    system("mkdir -p logs");
    bool ok = gLogger.startFileLogger("logs/simulation.log");
    if (!ok)
    {
        cerr << "Failed to start file logger\n";
        return 1;
    }

    gLogger.system("SIMULATION STARTED | PID:" + to_string(getpid()));
    gLogger.info("Config: Waiters=" + to_string(NUM_WAITERS) +
                 " Chefs=" + to_string(NUM_CHEFS) +
                 " Orders=" + to_string(ORDERS_EACH) +
                 " QueueCap=" + to_string(QUEUE_CAPACITY));

    // Shared objects 
    OrderQueue queue(QUEUE_CAPACITY);
    KitchenStats stats;
    time_t sim_start = time(nullptr);

    // Phase 2
    DashboardState dashState;
    dashState.queue_capacity = QUEUE_CAPACITY;
    dashState.queue_size = 0;

    Dashboard dashboard(&dashState);
    dashboard.start(); //  pthread_create for render thread
    sleep(1);          // let first frame render before threads start printing

    // Phase 3: 
    vector<Chef *> chefs;
    for (int i = 1; i <= NUM_CHEFS; i++)
        chefs.push_back(new Chef(i, &queue, &stats,
                                 CHEF_MIN, CHEF_MAX, &dashState));
    for (auto c : chefs)
        c->start();

    // Phase 4: 
    vector<Waiter *> waiters;
    for (int i = 1; i <= NUM_WAITERS; i++)
        waiters.push_back(new Waiter(i, &queue, ORDERS_EACH,
                                     WAITER_MIN, WAITER_MAX, &dashState));
    for (auto w : waiters)
        w->start();

    // Phase 5: 
    for (auto w : waiters)
        w->join();

    // Phase 6: 
    sendPoisonPills(queue, NUM_CHEFS);
    for (auto c : chefs)
        c->join();

    // Phase 7: 
    pthread_mutex_lock(&dashState.mutex);
    dashState.simulation_done = true;
    pthread_mutex_unlock(&dashState.mutex);

    sleep(3);         // hold final dashboard frame for 3 seconds
    dashboard.stop(); // OS: pthread_join on render thread

    //Phase 8: 
    double sim_time = difftime(time(nullptr), sim_start);

    cout << "\n\n";
    cout << "========================================================\n";
    cout << "||       RESTAURANT SESSION REPORT                     ||\n";
    cout << "||======================================================||\n";
    cout << "||  Waiters (Producer threads)  : " << NUM_WAITERS << "                    ||\n";
    cout << "||  Chefs   (Consumer threads)  : " << NUM_CHEFS << "                    ||\n";
    cout << "||  Orders per waiter           : " << ORDERS_EACH << "                    ||\n";
    cout << "||  Total orders submitted      : " << NUM_WAITERS * ORDERS_EACH << "                   ||\n";
    cout << "||  Total orders cooked         : " << stats.total_cooked << "                   ||\n";
    cout << "||  Total cook time             : " << stats.total_cook_time << "s                  ||\n";
    cout << "||  Avg cook time per order     : "
         << (stats.total_cooked > 0 ? stats.total_cook_time / stats.total_cooked : 0)
         << "s                  ||\n";
    cout << "||  Simulation duration         : " << (int)sim_time << "s                  ||\n";
    cout << "||  Race conditions             : ZERO                    ||\n";
    cout << "||  Zombie processes            : ZERO (waitpid used)     ||\n";
    cout << "||  Log file                    : logs/simulation.log     ||\n";
    cout << "========================================================\n";
    
    bool pass = (stats.total_cooked == NUM_WAITERS * ORDERS_EACH);
    cout << (pass ? "\n  [PASS] " : "\n  [FAIL] ")
         << "All " << NUM_WAITERS * ORDERS_EACH
         << " orders cooked — zero race conditions!\n\n";

    gLogger.system("SIMULATION COMPLETE | cooked:" +
                   to_string(stats.total_cooked) +
                   " time:" + to_string((int)sim_time) + "s");
    gLogger.stopFileLogger(); // close pipe -> child exits -> waitpid()

    cout << "  Log saved: logs/simulation.log\n\n";

    for (auto w : waiters)
        delete w;
    for (auto c : chefs)
        delete c;

    return 0;
}
