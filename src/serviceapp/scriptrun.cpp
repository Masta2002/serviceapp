#include "scriptrun.h"
#include "common.h"

// keep the tail: the URL is the last line of the output
static void appendCapped(std::string &buffer, const char *data)
{
    buffer.append(data);
    if (buffer.size() > SCRIPT_MAX_OUTPUT)
        buffer.erase(0, buffer.size() - SCRIPT_MAX_OUTPUT);
}

void scriptrun::stdoutAvail(const char *data)
{
    //eDebug("outData: %s", data);
    appendCapped(m_stdout, data);
}

void scriptrun::stderrAvail(const char *data)
{
    //eDebug("errData: %s", data);
    appendCapped(m_stderr, data);
}

void scriptrun::appClosed(int retval)
{
    //eDebug("scriptrun::appClosed: %d", retval);
    scriptEnded(retval);
}

scriptrun::scriptrun(const std::string &scriptPath, const std::vector<std::string> &params)
{
    m_scriptpath = scriptPath;
    m_params = params;
}

scriptrun::~scriptrun()
{
    stop();
}

void scriptrun::run(eMainloop *context)
{
    m_console = new eConsoleContainer();
    CONNECT(m_console->appClosed, scriptrun::appClosed);
    CONNECT(m_console->stdoutAvail, scriptrun::stdoutAvail);
    CONNECT(m_console->stderrAvail, scriptrun::stderrAvail);

    std::vector<std::string> args;
    args.push_back(m_scriptpath);
    for (size_t i = 0;  i < m_params.size(); i++)
        args.push_back(m_params[i]);

    std::vector<const char *> cargs(args.size()+1);
    for (size_t i=0; i <= args.size(); i++)
    {
        // execvp needs args array terminated with NULL
        if (i == args.size())
        {
            cargs[i] = NULL;
            eDebugNoNewLine("\n");
        }
        else
        {
            cargs[i] = args[i].c_str();
            if (i != 0 && cargs[i][0] != '-')
                eDebugNoNewLine("\"%s\" ", cargs[i]);
            else
                eDebugNoNewLine("%s ", cargs[i]);
        }
    }
    // vfork suspends the parent until execvp, so args/cargs may go after this
    int ret = m_console->execute(context, cargs[0], cargs.data());
    // if the process never started, appClosed will never fire and the
    // caller would wait forever; drive the end-of-script path ourselves
    if (ret < 0)
        scriptEnded(-1);
}

void scriptrun::stop()
{
    if (m_console && m_console->running())
        m_console->sendCtrlC();
}

void scriptrun::kill()
{
    /* SIGKILL, the pipes stay open: their hangup ends the script run */
    if (m_console)
        m_console->sendKill();
}

ResolveUrl::ResolveUrl(const std::string &url):

    m_url(url),
    m_success(0),
    mThreadRunning(false),
    mStopped(false),
    mTimedOut(false),
    mEnded(false),
    mMessageMain(eApp, 1, "scriptrun"),
    mMessageThread(this, 1, "scriptrun"),
    mWaitForStop(false)
{
    eDebug("ResolveUrl::ResolveUrl %s", url.c_str());
    CONNECT(mMessageThread.recv_msg, ResolveUrl::gotMessage);
    CONNECT(mMessageMain.recv_msg, ResolveUrl::gotMessage);
    pthread_mutex_init(&mWaitForStopMutex, NULL);
    pthread_cond_init(&mWaitForStopCond, NULL);
}

ResolveUrl::~ResolveUrl()
{
    stop();
    pthread_mutex_destroy(&mWaitForStopMutex);
    pthread_cond_destroy(&mWaitForStopCond);
    // the thread is gone (stop() joined it), nothing uses the timer any more
    mTimeoutTimer = 0;
    // before the message pumps, as the old delete here did
    m_scriptrun.reset();
}

void ResolveUrl::start()
{
    std::vector<std::string> params;
    std::string delimiter = "|";
    size_t last = 0; size_t next = 0;
    while ((next = m_url.find(delimiter, last)) != std::string::npos)
    {
        params.push_back(m_url.substr(last, next-last));
        last = next + 1;
    }
    params.push_back(m_url.substr(last));

    m_scriptrun = std::make_unique<scriptrun>("/etc/enigma2/script", params);

    // start player only when mainloop in player thread has started
    mMessageThread.send(Message(Message::tStart));
    run();
}

void ResolveUrl::stop()
{
    mStopped = true;
    if (mThreadRunning)
    {
        // SIGINT, wait up to 10s for the script to end (scriptEnded clears
        // mWaitForStop), then SIGKILL - otherwise a hung script that ignores
        // SIGINT blocks the join below and the enigma2 main thread forever
        mWaitForStop = true;
        WaitThread t(mWaitForStopMutex, mWaitForStopCond, mWaitForStop, 10000);
        t.run();
        mMessageThread.send(Message(Message::tStop));
        t.kill();
        if (t.isTimedOut() && m_scriptrun)
            m_scriptrun->kill();
    }
    kill();
}

std::string ResolveUrl::getUrl()
{
    std::string output = m_scriptrun->getStdOut();
    // the URL is the last non-empty line: anything the script printed
    // before it (progress, debug) must not end up inside the URL with its
    // newlines. Only whitespace around it goes, a script using printf
    // without a newline keeps its last character.
    size_t lineEnd = output.find_last_not_of(" \t\r\n");
    if (lineEnd == std::string::npos)
        return "";
    size_t lineStart = output.find_last_of("\r\n", lineEnd);
    lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
    lineStart = output.find_first_not_of(" \t", lineStart);
    return output.substr(lineStart, lineEnd - lineStart + 1);
}

void ResolveUrl::resolveTimeout()
{
    if (!mTimedOut)
    {
        eDebug("ResolveUrl - script still running after %d ms, killing it", RESOLVE_TIMEOUT_MS);
        mTimedOut = true;
        if (m_scriptrun)
            m_scriptrun->kill();
        // its pipes' hangup normally ends the run through scriptEnded()
        mTimeoutTimer->start(RESOLVE_KILL_GRACE_MS, true);
        return;
    }
    // a descendant in a session of its own still holds the pipes: report
    // the failure anyway, the service must not wait for it
    eDebug("ResolveUrl - script output still open after the kill, giving up");
    scriptEnded(-1);
}

void ResolveUrl::scriptEnded(int retval)
{
    // once only: after the give-up of resolveTimeout() a late hangup of
    // the pipes must not report a second result
    if (mEnded)
        return;
    mEnded = true;
    if (mTimeoutTimer)
        mTimeoutTimer->stop();
    pthread_mutex_lock(&mWaitForStopMutex);
    if (mWaitForStop)
    {
        mWaitForStop = false;
        pthread_cond_signal(&mWaitForStopCond);
    }
    pthread_mutex_unlock(&mWaitForStopMutex);
    quit(0);
    // killed by the timeout: a SIGKILL'd script may still report 0 and
    // have printed half a URL
    if (mStopped || mTimedOut)
        m_success = false;
    else
    {
        m_success = !retval;
        if (m_success)
            m_success = !getUrl().empty();
    }
    mMessageMain.send(Message(Message::stop));
}

void ResolveUrl::thread()
{
    mThreadRunning = true;
    hasStarted();
    runLoop();
}

void ResolveUrl::thread_finished()
{
    //eDebug("ResolveUrl::thread_finished");
    mThreadRunning = false;
    // stop() may have started waiting after scriptEnded() had already run
    pthread_mutex_lock(&mWaitForStopMutex);
    if (mWaitForStop)
    {
        mWaitForStop = false;
        pthread_cond_signal(&mWaitForStopCond);
    }
    pthread_mutex_unlock(&mWaitForStopMutex);
}

void ResolveUrl::gotMessage(const ResolveUrl::Message &message)
{
    switch (message.type)
    {
    case Message::tStart:
        //eDebug("ResolveUrl::gotMessage - tStart");
        // created here, in the resolve thread: the timer belongs to its
        // mainloop, like the console of the script
        mTimeoutTimer = eTimer::create(this);
        CONNECT(mTimeoutTimer->timeout, ResolveUrl::resolveTimeout);
        mTimeoutTimer->start(RESOLVE_TIMEOUT_MS, true);
        CONNECT(m_scriptrun->scriptEnded, ResolveUrl::scriptEnded);
        m_scriptrun->run(this);
        break;
    case Message::tStop:
        eDebug("ResolveUrl::gotMessage - tStop");
        m_scriptrun->stop();
        break;
    case Message::stop:
        eDebug("ResolveUrl::gotMessage - stop");
        urlResolved(m_success);
        break;
    default:
        break;
    }
}
