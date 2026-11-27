#ifndef __LIB_MY_CONSOLE_H__
#define __LIB_MY_CONSOLE_H__

#include <string>
#include <lib/base/ebase.h>
#include <lib/python/connections.h>
#include <deque>
#include <vector>

struct queue_data
{
	queue_data( const char *data, int len, bool droppable = false )
		:data(data, data+len), len(len), dataSent(0), droppable(droppable)
	{
	}
	std::vector<char> data;
	int len;
	int dataSent;
	/* a periodic poll the next tick repeats; it may go when the queue is full */
	bool droppable;
};

class eConsoleContainer: public sigc::trackable, public iObject
{
	DECLARE_REF(eConsoleContainer);
	int fd[3];
	int filefd[3];
	int pid;
	int killstate;
	std::string m_cwd;
	std::deque<struct queue_data> outbuf;
	ePtr<eSocketNotifier> in, out, err;
	std::vector<char> buffer;
	void readyRead(int what);
	void readyErrRead(int what);
	void readyWrite(int what);
	void closePipes();
public:
	eConsoleContainer();
	~eConsoleContainer();
	int setCWD( const char *path );
	void setBufferSize(int size);
	int execute( eMainloop * context, const char *str );
	int execute( eMainloop * context, const char *cmdline, const char *const argv[] );
	int getPID() { return pid; }
	void kill();
	void sendCtrlC();
	void sendKill();
	void sendEOF();
	/* droppable: a poll command that may be dropped while the app does not
	 * read its stdin (the queue holds at most OUTBUF_MAX of them) */
	void write( const char *data, int len, bool droppable = false );
	void setFileFD(int num, int fd) { if (num >= 0 && num <= 2) filefd[num] = fd; }
	bool running() { return (fd[0]!=-1) && (fd[1]!=-1) && (fd[2]!=-1); }
	PSignal1<void, const char*> dataAvail;
	PSignal1<void, const char*> stdoutAvail;
	PSignal1<void, const char*> stderrAvail;
	/* the same chunks with their length: a NUL byte in the output does not
	 * cut them short */
	sigc::signal<void(const char*, int)> stdoutData;
	sigc::signal<void(const char*, int)> stderrData;
	PSignal1<void,int> dataSent;
	/* exit code of the app, 128 + signal number when a signal ended it */
	PSignal1<void,int> appClosed;
};

#endif // __LIB_MY_CONSOLE_H__
