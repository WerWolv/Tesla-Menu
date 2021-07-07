#pragma once

#include <switch.h>

namespace netloader {

    typedef struct {
        bool activated;
        bool launch_app;
        bool transferring;
        bool sock_connected;
        size_t filelen, filetotal;
        char errormsg[1025];
    } State;

    void task(void* arg);

    void getState(State *state);
    void signalExit();

    Result setNext();

}
