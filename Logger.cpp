#include "Logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <cstring>
#include <cerrno>
#include <sys/wait.h>
#include <cstdint>

using namespace std;

//  Global logger instance  ,shared by all threads
Logger gLogger;

Logger::Logger()
    : child_pid(-1), file_logger_running(false)
{
    pipe_fd[0] = pipe_fd[1] = -1;
    pthread_mutex_init(&console_mutex, nullptr);
}

Logger::~Logger()
{
    if (file_logger_running)
        stopFileLogger();
    pthread_mutex_destroy(&console_mutex);
}

//  getTimestamp()
//  Returns current time 
//  Uses time() + localtime(),OS time system calls.
string Logger::getTimestamp() const
{
    time_t now = time(nullptr); //  time() system call
    struct tm *t = localtime(&now);
    ostringstream oss;
    oss << setw(2) << setfill('0') << t->tm_hour << ":"
        << setw(2) << setfill('0') << t->tm_min << ":"
        << setw(2) << setfill('0') << t->tm_sec;
    return oss.str();
}

//  levelToString()
string Logger::levelToString(LogLevel level) const
{
    switch (level)
    {
    case LOG_INFO:
        return "INFO  ";
    case LOG_ORDER:
        return "ORDER ";
    case LOG_WARN:
        return "WARN  ";
    case LOG_ERROR:
        return "ERROR ";
    case LOG_SYSTEM:
        return "SYSTEM";
    default:
        return "LOG   ";
    }
}

//  writeToConsole()
//  Mutex protected cout , prevents interleaved output from threads.
void Logger::writeToConsole(const string &formatted)
{
    pthread_mutex_lock(&console_mutex); 
    cout << formatted << endl;
    pthread_mutex_unlock(&console_mutex);
}

/*
writeToPipe()
Sends a log string to the child process via the pipe.
Uses write() — a low-level POSIX I/O system call.
Protocol: we send the string length (4 bytes) then the string data.
Child reads length first, then reads exactly that many bytes.
This prevents partial reads on the child side.
*/
void Logger::writeToPipe(const string &formatted)
{
    if (!file_logger_running || pipe_fd[1] < 0)
        return;

    // Send length prefix (so child knows how many bytes to read)
    uint32_t len = formatted.size();

    // write() direct POSIX system call to kernel
    // Unlike cout, this bypasses C++ buffering entirely.
    pthread_mutex_lock(&console_mutex); // reuse mutex for pipe writes too
    write(pipe_fd[1], &len, sizeof(len));
    write(pipe_fd[1], formatted.c_str(), len);
    pthread_mutex_unlock(&console_mutex);
}


//  startFileLogger()
//  After fork():
// .Parent gets child's PID returned (> 0)
// . Child  gets 0 returned
// . Both have the SAME code, SAME variables (copied)
// . They run INDEPENDENTLY from this point

//  IMPORTANT: fork() must be called BEFORE pthread_create().
//  Mixing fork() and threads is dangerous forking copies
//  all mutexes in whatever state they're in, which can deadlock.
//  We fork first, threads come after.
bool Logger::startFileLogger(const string &filepath)
{

    if (pipe(pipe_fd) == -1)
    {
        cerr << "[Logger] pipe() failed: " << strerror(errno) << endl;
        return false;
    }

    child_pid = fork();

    if (child_pid < 0)
    {
        cerr << "[Logger] fork() failed: " << strerror(errno) << endl;
        return false;
    }

    if (child_pid == 0)
    {
        close(pipe_fd[1]); 

        pid_t my_pid = getpid();     
        pid_t parent_pid = getppid(); 

        // Open log file for writing
        ofstream logfile(filepath, ios::out | ios::trunc);
        if (!logfile.is_open())
        {
            cerr << "[Child Logger] Cannot open log file: " << filepath << endl;
            _exit(1); 
        }

        // Write header to log file
        logfile << "========================================================" << endl;
        logfile << "  Restaurant Order Management Simulator — Log File" << endl;
        logfile << "  Child Logger PID  : " << my_pid << endl;
        logfile << "  Parent Sim  PID   : " << parent_pid << endl;
        logfile << "  Log file          : " << filepath << endl;
        logfile << "========================================================" << endl;
        logfile.flush();

        cout << "\n[CHILD LOGGER] Process started."
             << "\n  Child PID  : " << my_pid
             << "\n  Parent PID : " << parent_pid
             << "\n  Writing to : " << filepath << "\n"
             << endl;

        //  Child's main loop: read from pipe, write to file 
        while (true)
        {
            uint32_t len = 0;
            ssize_t r = read(pipe_fd[0], &len, sizeof(len));

            if (r <= 0)
                break; 

            string msg(len, '\0');
            ssize_t total = 0;
            while (total < (ssize_t)len)
            {
                ssize_t got = read(pipe_fd[0], &msg[total], len - total);
                if (got <= 0)
                    goto child_done;
                total += got;
            }

            // Write to file
            logfile << msg << "\n";
            logfile.flush(); // flush immediately so nothing is lost
        }

    child_done:
        logfile << "\n[Child Logger] Pipe closed. Exiting." << endl;
        logfile.close();
        close(pipe_fd[0]);

        cout << "[CHILD LOGGER] Log file written and closed. Process exiting." << endl;

        // terminate child process
        _exit(0);
    }
    else
    {
        //  PARENT PROCESS 
        close(pipe_fd[0]); 
        file_logger_running = true;

        pid_t my_pid = getpid(); 
        cout << "[PARENT] Simulation process."
             << "\n  Parent PID     : " << my_pid
             << "\n  Child Logger   : " << child_pid
             << "\n  Pipe write end : fd[" << pipe_fd[1] << "]"
             << "\n  File logging   : " << filepath << "\n"
             << endl;

        return true;
    }
}

//  stopFileLogger()
void Logger::stopFileLogger()
{
    if (!file_logger_running)
        return;

    // This sends EOF to the child , its read() returns 0 → it exits.
    if (pipe_fd[1] >= 0)
    {
        close(pipe_fd[1]);
        pipe_fd[1] = -1;
    }

    file_logger_running = false;

    int status = 0;
    cout << "\n[PARENT] Waiting for child logger (PID " << child_pid << ") to exit..." << endl;

    waitpid(child_pid, &status, 0);

    if (WIFEXITED(status))
    {
        cout << "[PARENT] Child logger exited normally with code: "
             << WEXITSTATUS(status) << endl;
    }

    child_pid = -1;
}

//  log() main logging function
//  Called by any thread. Thread-safe via mutex.
//  Formats the message, writes to console AND pipe (→ file).
void Logger::log(LogLevel level, const string &message)
{
    // Build formatted log line
    ostringstream oss;
    oss << "[" << getTimestamp() << "]"
        << "[" << levelToString(level) << "]"
        << "[PID:" << getpid() << "]" //getpid() per log line
        << " " << message;

    string formatted = oss.str();

    // Write to console (mutex-protected)
    writeToConsole(formatted);

    // Write to file via pipe → child process
    writeToPipe(formatted);
}

//  section() — prints a visual separator
void Logger::section(const string &title)
{
    ostringstream oss;
    oss << "\n-------------------------------------" << "\n"
        << "  " << title << "\n"
        << "--------------------------------------";
    writeToConsole(oss.str());
    writeToPipe(oss.str());
}
