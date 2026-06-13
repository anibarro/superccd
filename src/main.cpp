#include <QApplication>
#include "MainWindow.h"
#include "SuperCCDProcessor.h"
#include <csignal>
#include <algorithm>
#include <exception>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include <QCoreApplication>
#include <QDateTime>
#include <QIcon>

// APP_VERSION_STRING is defined by CMake via -DAPP_VERSION_STRING=...
// If not defined (e.g., standalone compilation), use a default
#ifndef APP_VERSION_STRING
#define APP_VERSION_STRING "0.1.9"
#endif

static void write_log_message(const char *msg)
{
#if defined(_WIN32)
    // Use Win32 API to avoid C runtime buffering issues in signal handlers
    HANDLE h = CreateFileA("last_error.log", FILE_APPEND_DATA, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        // Prepend timestamp
        char buf[512];
        SYSTEMTIME st;
        GetLocalTime(&st);
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d - %s\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);
        WriteFile(h, buf, (DWORD)strlen(buf), &written, NULL);
        CloseHandle(h);
    }
#elif defined(__APPLE__) || defined(__unix__) || defined(__linux__)
    // macOS and Linux: use POSIX file I/O with flock for safe concurrent writes
    FILE *f = fopen("last_error.log", "a");
    if (f) {
        // Try to acquire exclusive lock (non-blocking)
        struct flock fl;
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;
        
        if (fcntl(fileno(f), F_SETLK, &fl) == 0) {
            time_t t = time(NULL);
            struct tm tm;
            localtime_r(&t, &tm);
            char timestr[64];
            strftime(timestr, sizeof(timestr), "%Y-%m-%dT%H:%M:%S", &tm);
            fprintf(f, "%s - %s\n", timestr, msg);
            fflush(f);
            
            // Release lock
            fl.l_type = F_UNLCK;
            fcntl(fileno(f), F_SETLK, &fl);
        }
        fclose(f);
    }
#else
    // Fallback for other platforms
    FILE *f = fopen("last_error.log", "a");
    if (f) {
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        char timestr[64];
        strftime(timestr, sizeof(timestr), "%Y-%m-%dT%H:%M:%S", &tm);
        fprintf(f, "%s - %s\n", timestr, msg);
        fclose(f);
    }
#endif
}

static void signal_handler(int signum)
{
    char msg[128];
    snprintf(msg, sizeof(msg), "Fatal signal %d", signum);
    write_log_message(msg);
    // restore default and re-raise
    std::signal(signum, SIG_DFL);
    std::raise(signum);
}

static void my_terminate_handler()
{
    try {
        if (std::current_exception()) {
            std::rethrow_exception(std::current_exception());
        }
    } catch (const std::exception &ex) {
        write_log_message(ex.what());
    } catch (...) {
        write_log_message("Unknown terminate exception");
    }
    std::_Exit(1);
}

static bool is_version_option(const char *argument)
{
    return std::strcmp(argument, "--version") == 0
        || std::strcmp(argument, "-v") == 0;
}

static void print_version()
{
    char message[128];
    const int length = std::snprintf(message,
                                     sizeof(message),
                                     "SuperCCD2DNG version %s\n",
                                     APP_VERSION_STRING);
    if (length <= 0) {
        return;
    }

#if defined(_WIN32)
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    bool attachedConsole = false;
    if (output == nullptr || output == INVALID_HANDLE_VALUE) {
        attachedConsole = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
        output = GetStdHandle(STD_OUTPUT_HANDLE);
    }

    if (output != nullptr && output != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        const int boundedLength = length < static_cast<int>(sizeof(message))
            ? length
            : static_cast<int>(sizeof(message) - 1);
        WriteFile(output,
                  message,
                  static_cast<DWORD>(boundedLength),
                  &written,
                  nullptr);
    }

    if (attachedConsole) {
        FreeConsole();
    }
#else
    std::fputs(message, stdout);
#endif
}

int main(int argc, char *argv[])
{
    // Install handlers to catch crashes that don't throw C++ exceptions
    std::set_terminate(my_terminate_handler);
    std::signal(SIGABRT, signal_handler);
    std::signal(SIGSEGV, signal_handler);
    std::signal(SIGFPE, signal_handler);
    std::signal(SIGILL, signal_handler);

    QCoreApplication::setApplicationName(QStringLiteral("SuperCCD RAF to DNG Converter"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(APP_VERSION_STRING));

    for (int i = 1; i < argc; ++i) {
        if (is_version_option(argv[i])) {
            print_version();
            return 0;
        }
    }

    if (argc >= 3) {
        QCoreApplication app(argc, argv);
        app.setApplicationName(QCoreApplication::applicationName());
        app.setApplicationVersion(QCoreApplication::applicationVersion());
        ConversionSettings settings;
        for (int i = 3; i < argc; ++i) {
            if (std::strcmp(argv[i], "--6mp") == 0 || std::strcmp(argv[i], "--6mp-cfa") == 0) {
                settings.exportMode = ExportMode::RawCfa6MP;
            } else if (std::strcmp(argv[i], "--12mp-linear") == 0) {
                settings.exportMode = ExportMode::Linear12MPExperimental;
            } else if (std::strncmp(argv[i], "--linear-chroma=", 16) == 0) {
                settings.linearChromaSuppression = std::clamp(std::atof(argv[i] + 16), 0.0, 1.0);
            } else if (std::strncmp(argv[i], "--delay=", 8) == 0) {
                settings.rTransitionDelay = std::clamp(std::atof(argv[i] + 8), 0.0, 1.0);
            } else if (std::strncmp(argv[i], "--smoothness=", 12) == 0) {
                settings.rTransitionSmoothness = std::clamp(std::atof(argv[i] + 12), 0.0, 1.0);
            }
        }

        SuperCCDProcessor processor;
        QString error;
        if (!processor.process(QString::fromLocal8Bit(argv[1]),
                               QString::fromLocal8Bit(argv[2]),
                               settings,
                               error)) {
            fprintf(stderr, "Conversion failed: %s\n", error.toUtf8().constData());
            return 2;
        }
        return 0;
    }

    QApplication app(argc, argv);
    app.setApplicationName(QCoreApplication::applicationName());
    app.setApplicationVersion(QCoreApplication::applicationVersion());
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app_icon_256.png")));
    MainWindow window;
    window.show();
    return app.exec();
}
