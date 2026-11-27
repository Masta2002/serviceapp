#ifndef SCRIPTRUN_H
#define SCRIPTRUN_H

#include <lib/base/ebase.h>
#include <lib/base/message.h>
#include <lib/python/connections.h>
#include <lib/base/thread.h>

#include <memory>

#include "extplayer.h"
#include "myconsole.h"

/* stdout/stderr of the script kept in memory: the URL is the last line,
 * a script printing a lot before it only costs memory */
#define SCRIPT_MAX_OUTPUT ((size_t) 64 * 1024)
/* the service waits for the resolved URL: a hanging script is killed and
 * reported as failure instead of leaving the service waiting forever */
#define RESOLVE_TIMEOUT_MS 30000
/* after the kill, the hangup of the script's pipes normally ends the run;
 * a descendant that keeps them open is given up on after this */
#define RESOLVE_KILL_GRACE_MS 5000

class scriptrun: public sigc::trackable
{
    std::vector<std::string> m_params;
    std::string m_scriptpath;
    std::string m_stdout;
    std::string m_stderr;
    ePtr<eConsoleContainer> m_console;

    void stdoutAvail(const char *data);
    void stderrAvail(const char *data);
    void appClosed(int retval);
public:
    scriptrun(const std::string &scriptPath,
              const std::vector<std::string> &params);
    ~scriptrun();
    void run(eMainloop *context);
    void stop();
    void kill();

    std::string getStdOut(){return m_stdout;}
    std::string getStdErr(){return m_stderr;}

    PSignal1<void, int> scriptEnded;
};


class ResolveUrl: public sigc::trackable, public eThread, public eMainloop
{
    struct Message
    {
        int type;
        enum
        {
            start,
            tStart,
            stop,
            tStop,
        };
        Message(int type)
            :type(type) {}
    };
    std::unique_ptr<scriptrun> m_scriptrun;
    std::string m_url;
    int m_success;

    bool mThreadRunning;
    bool mStopped;
    // resolve thread only: timeout timer state, scriptEnded() reported
    ePtr<eTimer> mTimeoutTimer;
    bool mTimedOut;
    bool mEnded;

    eFixedMessagePump<Message> mMessageMain, mMessageThread;
    pthread_mutex_t mWaitForStopMutex;
    pthread_cond_t mWaitForStopCond;
    bool mWaitForStop;


    // eThread
    void thread();
    void thread_finished();
    void resolveTimeout();

public:
    ResolveUrl(const std::string &url);
    ~ResolveUrl();
    void gotMessage(const Message &message);

    void start();
    void stop();
    std::string getUrl();

    void scriptEnded(int retval);
    sigc::signal<void(int)> urlResolved;
};

#endif // SCRIPTRUN_H
