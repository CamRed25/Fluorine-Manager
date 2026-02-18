// Standalone process helper for Flatpak game launching.
// Runs on the host via flatpak-spawn --host, keeping the flatpak-spawn
// proxy alive for MO2's PID polling while the game process tree is running.
//
// Protocol (stdin/stdout, line-oriented):
//   Config phase: MO2 writes key=value lines terminated by a blank line
//     program=<path>, arg=<value> (repeatable), env=KEY=VALUE (repeatable),
//     workdir=<path>
//   Helper responds: "started <pid>" or "error <message>"
//   Runtime commands (MO2→helper): "kill" (SIGTERM child tree), "quit"
//   Helper reports: "exited <code>" when game process tree exits

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

// ── Helpers ──

static void writeResponse(const std::string& msg)
{
  std::string line = msg + "\n";
  [[maybe_unused]] ssize_t n = ::write(STDOUT_FILENO, line.data(), line.size());
}

static bool readLine(std::string& out, int timeoutMs)
{
  out.clear();

  struct pollfd pfd{};
  pfd.fd     = STDIN_FILENO;
  pfd.events = POLLIN;

  while (true) {
    int ret = ::poll(&pfd, 1, timeoutMs);
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (ret == 0) {
      // timeout
      return false;
    }

    // Try reading if data is available, even if HUP is also set
    // (pipe can have buffered data when the writer closes).
    if (!(pfd.revents & POLLIN)) {
      // No data available — must be HUP or ERR only
      return false;
    }

    char ch = 0;
    ssize_t n = ::read(STDIN_FILENO, &ch, 1);
    if (n <= 0) {
      return false;
    }
    if (ch == '\n') {
      return true;
    }
    out.push_back(ch);
  }
}

// Collect all descendant PIDs of a given root by scanning /proc.
static std::unordered_set<pid_t> collectDescendants(pid_t root)
{
  std::unordered_set<pid_t> descendants;

  // Build parent→children map
  struct ProcEntry {
    pid_t pid;
    pid_t ppid;
  };
  std::vector<ProcEntry> entries;

  DIR* proc = opendir("/proc");
  if (!proc) {
    return descendants;
  }

  struct dirent* entry = nullptr;
  while ((entry = readdir(proc)) != nullptr) {
    if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) {
      continue;
    }

    char* end = nullptr;
    long pidLong = strtol(entry->d_name, &end, 10);
    if (end == entry->d_name || *end != '\0' || pidLong <= 0) {
      continue;
    }

    pid_t pid = static_cast<pid_t>(pidLong);

    char statusPath[64];
    snprintf(statusPath, sizeof(statusPath), "/proc/%ld/status", pidLong);

    std::ifstream status(statusPath);
    if (!status.is_open()) {
      continue;
    }

    std::string line;
    pid_t ppid = 0;
    while (std::getline(status, line)) {
      if (line.rfind("PPid:", 0) == 0) {
        ppid = static_cast<pid_t>(strtol(line.c_str() + 5, nullptr, 10));
        break;
      }
    }

    if (ppid > 0) {
      entries.push_back({pid, ppid});
    }
  }
  closedir(proc);

  // BFS from root
  std::vector<pid_t> queue;
  queue.push_back(root);

  while (!queue.empty()) {
    pid_t cur = queue.back();
    queue.pop_back();

    for (const auto& e : entries) {
      if (e.ppid == cur && descendants.find(e.pid) == descendants.end()) {
        descendants.insert(e.pid);
        queue.push_back(e.pid);
      }
    }
  }

  return descendants;
}

// Check if any process in the given set is still alive.
static bool anyAlive(const std::unordered_set<pid_t>& pids)
{
  for (pid_t p : pids) {
    if (::kill(p, 0) == 0 || errno == EPERM) {
      return true;
    }
  }
  return false;
}

// ── Main ──

int main()
{
  // Make stdout line-buffered for reliable protocol messages.
  setvbuf(stdout, nullptr, _IOLBF, 0);

  // ── Config phase: read key=value lines until blank line ──
  std::string program;
  std::vector<std::string> args;
  std::vector<std::string> envVars;  // "KEY=VALUE"
  std::string workdir;

  while (true) {
    std::string line;
    if (!readLine(line, 30000)) {
      writeResponse("error stdin closed or timeout during config");
      return 1;
    }

    if (line.empty()) {
      break;  // blank line = end of config
    }

    auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }

    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);

    if (key == "program") {
      program = val;
    } else if (key == "arg") {
      args.push_back(val);
    } else if (key == "env") {
      envVars.push_back(val);
    } else if (key == "workdir") {
      workdir = val;
    }
  }

  if (program.empty()) {
    writeResponse("error no program specified");
    return 1;
  }

  // ── Pipe for exec error reporting ──
  // Child writes errno to this pipe if execvp fails; parent reads it.
  int errPipe[2];
  if (::pipe2(errPipe, O_CLOEXEC) != 0) {
    writeResponse("error pipe2 failed: " + std::string(strerror(errno)));
    return 1;
  }

  // ── Fork ──
  pid_t child = ::fork();
  if (child < 0) {
    writeResponse("error fork failed: " + std::string(strerror(errno)));
    return 1;
  }

  if (child == 0) {
    // ── Child ──
    ::close(errPipe[0]);  // close read end

    // New session so we can kill the whole process group later.
    ::setsid();

    // Set environment variables.
    for (const auto& ev : envVars) {
      ::putenv(const_cast<char*>(ev.c_str()));
    }

    // Change working directory.
    if (!workdir.empty()) {
      if (::chdir(workdir.c_str()) != 0) {
        int err = errno;
        [[maybe_unused]] ssize_t n = ::write(errPipe[1], &err, sizeof(err));
        ::_exit(127);
      }
    }

    // Build argv for execvp.
    std::vector<const char*> argv;
    argv.push_back(program.c_str());
    for (const auto& a : args) {
      argv.push_back(a.c_str());
    }
    argv.push_back(nullptr);

    ::execvp(program.c_str(), const_cast<char* const*>(argv.data()));

    // If we get here, exec failed.
    int err = errno;
    [[maybe_unused]] ssize_t n = ::write(errPipe[1], &err, sizeof(err));
    ::_exit(127);
  }

  // ── Parent ──
  ::close(errPipe[1]);  // close write end

  // Check if exec succeeded (pipe closes on successful exec due to O_CLOEXEC).
  int execErr = 0;
  ssize_t n   = ::read(errPipe[0], &execErr, sizeof(execErr));
  ::close(errPipe[0]);

  if (n > 0) {
    // exec failed in child
    ::waitpid(child, nullptr, 0);
    writeResponse("error exec failed: " + std::string(strerror(execErr)));
    return 1;
  }

  // Success - report PID.
  writeResponse("started " + std::to_string(child));

  // ── Monitor loop ──
  // Wait for the direct child and then monitor descendants (handles Proton
  // chain: proton → wine → game.exe).
  bool childExited    = false;
  int childStatus     = 0;
  bool quit           = false;

  while (!quit) {
    // Check for commands on stdin.
    std::string cmd;
    // Non-blocking: if readLine returns false due to timeout, that's fine.
    // If it returns false due to pipe close, MO2 crashed — kill child group.
    struct pollfd pfd{};
    pfd.fd     = STDIN_FILENO;
    pfd.events = POLLIN;

    int pollRet = ::poll(&pfd, 1, 200);

    if (pollRet > 0) {
      if (pfd.revents & (POLLHUP | POLLERR)) {
        // MO2 crashed or closed pipe — kill child group and exit.
        ::kill(-child, SIGTERM);
        break;
      }

      if (pfd.revents & POLLIN) {
        if (readLine(cmd, 0)) {
          if (cmd == "kill") {
            ::kill(-child, SIGTERM);
          } else if (cmd == "quit") {
            quit = true;
            break;
          }
        } else {
          // read failed = pipe closed
          ::kill(-child, SIGTERM);
          break;
        }
      }
    }

    // Check child status.
    if (!childExited) {
      int status  = 0;
      pid_t ret   = ::waitpid(child, &status, WNOHANG);
      if (ret == child) {
        childExited = true;
        childStatus = status;
      } else if (ret < 0 && errno != EINTR) {
        // Child somehow lost
        childExited = true;
        childStatus = 0;
      }
    }

    if (childExited) {
      // Check for surviving descendants (e.g., game.exe still running
      // after the proton wrapper exits).
      auto desc = collectDescendants(child);
      if (desc.empty() || !anyAlive(desc)) {
        // All done.
        int exitCode = 0;
        if (WIFEXITED(childStatus)) {
          exitCode = WEXITSTATUS(childStatus);
        } else if (WIFSIGNALED(childStatus)) {
          exitCode = 128 + WTERMSIG(childStatus);
        }
        writeResponse("exited " + std::to_string(exitCode));
        return 0;
      }
      // Descendants still alive, keep monitoring.
    }
  }

  // If we broke out of the loop, reap child if needed.
  if (!childExited) {
    ::waitpid(child, nullptr, 0);
  }

  writeResponse("exited 0");
  return 0;
}
