#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <pthread.h>
#include <unistd.h>     
#include <sys/wait.h>   
#include <sys/types.h>  
using namespace std;


// Log levels
enum LogLevel {
    LOG_INFO   = 0,   
    LOG_ORDER  = 1,   // Order events (placed, cooking, done)
    LOG_WARN   = 2,   // Warnings (queue near full)
    LOG_ERROR  = 3,   // Errors
    LOG_SYSTEM = 4    // OS/system call events
};

class Logger {
private:

    pthread_mutex_t console_mutex;

    int   pipe_fd[2];       
    pid_t child_pid;        
    bool  file_logger_running;

    // Internal helpers
    string levelToString(LogLevel level) const;
    string getTimestamp()                const;
    void   writeToConsole(const string& formatted);
    void   writeToPipe(const string& formatted);

public:
    Logger();
    ~Logger();


    bool startFileLogger(const string& filepath);

    void stopFileLogger();

    void log(LogLevel level, const string& message);

    void info  (const string& msg) { log(LOG_INFO,   msg); }
    void order (const string& msg) { log(LOG_ORDER,  msg); }
    void warn  (const string& msg) { log(LOG_WARN,   msg); }
    void error (const string& msg) { log(LOG_ERROR,  msg); }
    void system(const string& msg) { log(LOG_SYSTEM, msg); }

    // Print a formatted section separator
    void section(const string& title);

    // ── Getters ──
    pid_t getChildPid() const { return child_pid; }
};


//  Global logger instance
//  Declared here so Waiter.cpp and Chef.cpp can both use it.
//  Defined in Logger.cpp.
extern Logger gLogger;

#endif
