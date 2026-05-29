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
#endif
#include <QCoreApplication>
#include <QDateTime>
#include <QIcon>

static void write_log_message(const char *msg)
{
#ifdef _WIN32
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
#else
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

int main(int argc, char *argv[])
{
    // Install handlers to catch crashes that don't throw C++ exceptions
    std::set_terminate(my_terminate_handler);
    std::signal(SIGABRT, signal_handler);
    std::signal(SIGSEGV, signal_handler);
    std::signal(SIGFPE, signal_handler);
    std::signal(SIGILL, signal_handler);

    if (argc >= 3) {
        QCoreApplication app(argc, argv);
        ConversionSettings settings;
        for (int i = 3; i < argc; ++i) {
            if (std::strcmp(argv[i], "--6mp") == 0 || std::strcmp(argv[i], "--6mp-cfa") == 0) {
                settings.exportMode = ExportMode::RawCfa6MP;
            } else if (std::strcmp(argv[i], "--12mp-linear") == 0) {
                settings.exportMode = ExportMode::Linear12MPExperimental;
            } else if (std::strncmp(argv[i], "--linear-chroma=", 16) == 0) {
                settings.linearChromaSuppression = std::clamp(std::atof(argv[i] + 16), 0.0, 1.0);
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
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app_icon_256.png")));
    MainWindow window;
    window.show();
    return app.exec();
}
