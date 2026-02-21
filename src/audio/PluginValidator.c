/*
    PluginValidator — tiny out-of-process helper that attempts to dlopen
    a plugin bundle's executable.  If the library's static initialisers
    crash (SIGSEGV, SIGABRT, etc.) only this child process dies; the
    host app survives.

    Exit codes
    ----------
      42  success – dlopen loaded & closed without incident
      99  crash   – a signal handler fired during dlopen
       1  usage   – wrong number of arguments
       2  bundle  – couldn't find Contents/MacOS/ or binary inside it
       3  dlopen  – dlopen returned NULL (graceful failure, not a crash)
*/

#include <dlfcn.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void crash_handler (int sig)
{
    (void) sig;
    _exit (99);
}

int main (int argc, char* argv[])
{
    /* Catch common crash signals so we can report back cleanly
       instead of triggering ReportCrash / generating .ips files. */
    signal (SIGSEGV, crash_handler);
    signal (SIGABRT, crash_handler);
    signal (SIGBUS,  crash_handler);
    signal (SIGILL,  crash_handler);
    signal (SIGFPE,  crash_handler);

    if (argc < 2)
        return 1;

    const char* bundlePath = argv[1];

    /* Locate the binary inside Contents/MacOS/ */
    char macosDir[4096];
    snprintf (macosDir, sizeof (macosDir), "%s/Contents/MacOS", bundlePath);

    DIR* dir = opendir (macosDir);
    if (dir == NULL)
        return 2;

    char binaryPath[4096];
    binaryPath[0] = '\0';

    struct dirent* entry;
    while ((entry = readdir (dir)) != NULL)
    {
        if (entry->d_name[0] != '.')
        {
            snprintf (binaryPath, sizeof (binaryPath), "%s/%s",
                      macosDir, entry->d_name);
            break;
        }
    }
    closedir (dir);

    if (binaryPath[0] == '\0')
        return 2;

    /* Load the plugin binary — this triggers static initialisers,
       which is where problematic plugins crash. */
    void* handle = dlopen (binaryPath, RTLD_NOW);

    if (handle != NULL)
    {
        dlclose (handle);
        return 42;   /* success */
    }

    /* dlopen returned NULL (missing deps, arch mismatch, etc.)
       — not a crash, let the real scanner try its own way. */
    return 3;
}
